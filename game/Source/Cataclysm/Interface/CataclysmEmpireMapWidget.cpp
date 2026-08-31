// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireMapWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Player/CataclysmGameInstance.h"

namespace
{
	/** The name a city's button carries. The identifier, written out. */
	FName ButtonNameFor(int32 CityId)
	{
		return FName(*FString::Printf(TEXT("City%d"), CityId));
	}
}

// ---------------------------------------------------------------------------
// Building and redrawing
// ---------------------------------------------------------------------------

void UCataclysmEmpireMapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	Refresh();
}

void UCataclysmEmpireMapWidget::NativeTick(const FGeometry& MyGeometry,
										   float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	const UCataclysmEmpireRun* Run = ShownRun();
	const int32 Day = Run ? Run->Day() : INDEX_NONE;
	const FVector2D Size = CanvasSize();

	// ONLY WHEN SOMETHING MOVED. Rebuilding 25 boxes every frame would be
	// exactly the interface cost issue #825 is about, and the empire changes
	// only when a day passes -- which is never on its own, because nothing in
	// the game advances the day yet -- or when the panel is resized.
	if (Day != DrawnForDay || !Size.Equals(DrawnForSize, 1.0))
	{
		Refresh();
	}
}

void UCataclysmEmpireMapWidget::Refresh()
{
	if (CityButtons.IsEmpty())
	{
		BuildCities();
	}

	PlaceCities();
	WriteStatus();

	const UCataclysmEmpireRun* Run = ShownRun();
	DrawnForDay = Run ? Run->Day() : INDEX_NONE;
	DrawnForSize = CanvasSize();
}

UCataclysmEmpireRun* UCataclysmEmpireMapWidget::ShownRun() const
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

void UCataclysmEmpireMapWidget::SetRunForTests(UCataclysmEmpireRun* Run)
{
	RunForTests = Run;
}

void UCataclysmEmpireMapWidget::SetPanelSizeForTests(FVector2D Size)
{
	PanelSizeForTests = Size;
}

// ---------------------------------------------------------------------------
// The cities
// ---------------------------------------------------------------------------

void UCataclysmEmpireMapWidget::BuildCities()
{
	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr || Run->Map == nullptr || MapCanvas == nullptr)
	{
		return;
	}

	TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}

	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		UCataclysmChoiceButton* Button =
			CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
		if (!Button)
		{
			continue;
		}

		Button->OnChoiceHovered.AddDynamic(
			this, &UCataclysmEmpireMapWidget::HandleCityHovered);
		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmEmpireMapWidget::HandleCityClicked);

		// A LABEL TOO LONG FOR ITS BOX IS CUT, NOT DRAWN OVER THE NEXT CITY.
		// The whole of it is in the detail line under the map whenever the
		// cursor is over a city.
		Button->SetClipping(EWidgetClipping::ClipToBounds);

		if (UCanvasPanelSlot* Placement =
				Cast<UCanvasPanelSlot>(MapCanvas->AddChild(Button)))
		{
			// ALIGNED TO ITS OWN MIDDLE, so the position a city is given is the
			// city's centre. Anchoring by the corner would push the whole
			// diamond half a box down and right of where it was fitted.
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(FVector2D(0.5, 0.5));
			Placement->SetAutoSize(false);
		}

		CityButtons.Add(Button);
	}
}

void UCataclysmEmpireMapWidget::PlaceCities()
{
	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr || Run->Map == nullptr)
	{
		return;
	}

	const FVector2D Size = CanvasSize();
	const float Scale = UCataclysmEmpireMapLayout::ScaleToFit(Size);
	const FVector2D BoxSize = UCataclysmEmpireMapLayout::CitySize(Scale);

	for (int32 Index = 0; Index < CityButtons.Num(); ++Index)
	{
		UCataclysmChoiceButton* Button = CityButtons[Index];
		const FCataclysmCity* City = Run->Map->Find(Index);
		if (!Button || City == nullptr)
		{
			continue;
		}

		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			Placement->SetPosition(UCataclysmEmpireMapLayout::PositionFor(
				City->R, City->C, Scale, Size));
			Placement->SetSize(BoxSize);
		}

		const ECataclysmCityMark Mark =
			UCataclysmEmpireMapLayout::MarkFor(Run->Map, Index);

		// THREE STATES OUT OF THE CHOICE BUTTON'S OWN THREE APPEARANCES, rather
		// than three more colour properties on this screen. A fallen city is
		// unavailable and reads dim; an exposed one is chosen and reads bright,
		// because it is the one the player has to decide about; a sealed one is
		// plain.
		Button->SetChoice(
			ButtonNameFor(Index),
			FText::FromString(
				UCataclysmEmpireMapLayout::CityLabel(Run->Map, Index)),
			/*bChosen*/ Mark == ECataclysmCityMark::Exposed,
			/*bAvailable*/ Mark != ECataclysmCityMark::Fallen);
	}
}

void UCataclysmEmpireMapWidget::WriteStatus()
{
	const UCataclysmEmpireRun* Run = ShownRun();

	if (Run == nullptr || Run->Map == nullptr || Run->Surges == nullptr)
	{
		if (StatusLabel)
		{
			StatusLabel->SetText(NSLOCTEXT("Cataclysm", "EmpireNoRun",
										   "No run has been started."));
		}
		if (SurgeLabel)
		{
			SurgeLabel->SetText(FText::GetEmpty());
		}
		return;
	}

	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Day %d   %d cities lost of 24   %d dungeons standing"),
			Run->Day(), Run->Map->FallenCityCount(), Run->DungeonCount())));
	}

	if (SurgeLabel)
	{
		// DISTANCE TO DEFEAT AND NOT THE COUNT OF CITIES LOST, because that is
		// the number that decides the run: twenty Outposts scattered around the
		// rim cost less than three in a line.
		const FString Threat = Run->Map->IsPillarExposed()
			? FString(TEXT("The Cataclysm can reach the Pillar."))
			: FString::Printf(TEXT("%d cities from defeat."),
							  Run->Map->DistanceToDefeat());

		SurgeLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%s   Next surge in %.0f days, bringing %d."),
			*Threat,
			Run->Surges->DaysUntilNextSurge(Run->Day()),
			Run->Surges->DungeonsInNextSurge())));
	}
}

// ---------------------------------------------------------------------------
// The cursor
// ---------------------------------------------------------------------------

void UCataclysmEmpireMapWidget::HandleCityHovered(FName Value)
{
	if (DetailLabel == nullptr)
	{
		return;
	}

	const UCataclysmEmpireRun* Run = ShownRun();
	const int32 CityId = CityForButton(Value);

	if (Run == nullptr || Run->Map == nullptr || CityId == INDEX_NONE)
	{
		DetailLabel->SetText(FText::GetEmpty());
		return;
	}

	const FCataclysmCity* City = Run->Map->Find(CityId);
	if (City == nullptr)
	{
		DetailLabel->SetText(FText::GetEmpty());
		return;
	}

	TArray<FString> Parts;

	Parts.Add(City->Name);

	if (City->bFallen)
	{
		Parts.Add(TEXT("fallen"));
	}
	else
	{
		Parts.Add(FString::Printf(TEXT("%.0f of %.0f defence"),
								  City->Defence, City->MaxDefence));
		Parts.Add(FString::Printf(TEXT("%.0f of %.0f people"),
								  City->Population, City->MaxPopulation));
		Parts.Add(Run->Map->IsExposed(CityId) ? TEXT("exposed")
											  : TEXT("sealed behind the frontier"));
	}

	// WHAT IS STANDING ON IT AND HOW LONG EACH HAS. This is the whole triage
	// decision: which dungeon is about to detonate on which city.
	for (const int32 DungeonId : Run->DungeonsOn(CityId))
	{
		const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
		if (Dungeon == nullptr || Run->Clock == nullptr)
		{
			continue;
		}

		Parts.Add(FString::Printf(
			TEXT("dungeon of %d floors, %.0f days left"),
			Dungeon->Floors, Run->Clock->DaysUntilResolveFor(DungeonId)));
	}

	DetailLabel->SetText(FText::FromString(FString::Join(Parts, TEXT("   "))));
}

void UCataclysmEmpireMapWidget::HandleCityClicked(FName Value)
{
	// NOTHING YET, AND SAYING SO IS BETTER THAN DOING NOTHING SILENTLY. There is
	// no city screen to open, no upgrade to spend and no dungeon to enter; see
	// the class comment. A player who clicks a city and gets no response at all
	// would reasonably think the screen was broken.
	if (DetailLabel == nullptr)
	{
		return;
	}

	const int32 CityId = CityForButton(Value);
	const UCataclysmEmpireRun* Run = ShownRun();

	if (Run == nullptr || Run->Map == nullptr || CityId == INDEX_NONE)
	{
		return;
	}

	if (const FCataclysmCity* City = Run->Map->Find(CityId))
	{
		DetailLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%s. There is nothing to do at a city yet."), *City->Name)));
	}
}

int32 UCataclysmEmpireMapWidget::CityForButton(FName Value) const
{
	for (int32 Index = 0; Index < CityButtons.Num(); ++Index)
	{
		if (ButtonNameFor(Index) == Value)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

// ---------------------------------------------------------------------------
// What a test can read
// ---------------------------------------------------------------------------

FVector2D UCataclysmEmpireMapWidget::PlacedAt(int32 CityId) const
{
	if (!CityButtons.IsValidIndex(CityId) || CityButtons[CityId] == nullptr)
	{
		return FVector2D::ZeroVector;
	}

	if (const UCanvasPanelSlot* Placement =
			Cast<UCanvasPanelSlot>(CityButtons[CityId]->Slot))
	{
		return Placement->GetPosition();
	}

	return FVector2D::ZeroVector;
}

FString UCataclysmEmpireMapWidget::StatusText() const
{
	return StatusLabel ? StatusLabel->GetText().ToString() : FString();
}

FString UCataclysmEmpireMapWidget::SurgeText() const
{
	return SurgeLabel ? SurgeLabel->GetText().ToString() : FString();
}

FString UCataclysmEmpireMapWidget::DetailText() const
{
	return DetailLabel ? DetailLabel->GetText().ToString() : FString();
}

FVector2D UCataclysmEmpireMapWidget::CanvasSize() const
{
	// WHAT A TEST SAID IT IS, BEFORE ANYTHING ELSE. A headless test has no
	// geometry at all, so without this the two answers below are the same number
	// for ever and a test cannot make the panel change size. Zero means nothing
	// was said. Issue #1078 recorded this for the passive tree.
	if (!PanelSizeForTests.IsNearlyZero())
	{
		return PanelSizeForTests;
	}

	if (MapCanvas)
	{
		const FVector2D Measured = MapCanvas->GetCachedGeometry().GetLocalSize();
		if (Measured.X > 1.0 && Measured.Y > 1.0)
		{
			return Measured;
		}
	}

	// A COMMON WINDOW, so the first frame is roughly right rather than absurd.
	// A widget has no geometry until it has been laid out at least once, and the
	// first refresh happens in `NativeConstruct`, which is before that.
	return FVector2D(1600.0, 800.0);
}
